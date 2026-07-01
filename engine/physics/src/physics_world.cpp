#include "physics/physics_world.h"
#include "physics/collision.h"
#include "physics/manifold.h"
#include <cmath>
#include <new>

namespace ge::physics {

namespace {

Sphere body_sphere(const RigidBody& b) { return {b.position, b.radius}; }
Box body_box(const RigidBody& b) { return {b.position, b.half_extents, b.orientation}; }

Capsule body_capsule(const RigidBody& b) {
    mat4 r = quat_to_matrix(b.orientation);
    vec3 up{r.m[1][0], r.m[1][1], r.m[1][2]}; // local +Y axis in world space
    return {b.position - up * b.capsule_half_height, b.position + up * b.capsule_half_height,
            b.radius};
}

AABB body_aabb(const RigidBody& b) {
    switch (b.shape_type) {
        case ShapeType::Sphere:  return aabb_from_sphere(body_sphere(b));
        case ShapeType::Box:     return aabb_from_box(body_box(b));
        case ShapeType::Capsule: return aabb_from_capsule(body_capsule(b));
    }
    return {};
}

/**
 * @brief Dispatch to the right narrowphase+manifold pair by shape type, always a -> b.
 *
 * Covers every pair the current scene set (sphere, box, capsule-vs-box for
 * the character controller) actually needs. Capsule-sphere and
 * capsule-capsule aren't implemented yet — no such pair exists in Phase 5's
 * scope (a single capsule character over box terrain) — so they fall back
 * to an invalid Manifold (no collision response) rather than the wrong
 * physics; add them here if a future scene needs them.
 */
Manifold compute_manifold(const RigidBody& a, const RigidBody& b) {
    if (a.shape_type == ShapeType::Sphere && b.shape_type == ShapeType::Sphere)
        return manifold_sphere_sphere(body_sphere(a), body_sphere(b));

    if (a.shape_type == ShapeType::Sphere && b.shape_type == ShapeType::Box)
        return manifold_sphere_box(body_sphere(a), body_box(b));
    if (a.shape_type == ShapeType::Box && b.shape_type == ShapeType::Sphere) {
        // manifold_sphere_box()'s normal points sphere -> box; flip it so
        // this function keeps the a -> b convention even with the shapes
        // swapped for the underlying call.
        Manifold m = manifold_sphere_box(body_sphere(b), body_box(a));
        if (m.valid) m.normal = -m.normal;
        return m;
    }

    if (a.shape_type == ShapeType::Box && b.shape_type == ShapeType::Box)
        return manifold_box_box(body_box(a), body_box(b));

    if (a.shape_type == ShapeType::Capsule && b.shape_type == ShapeType::Box)
        return manifold_capsule_box(body_capsule(a), body_box(b));
    if (a.shape_type == ShapeType::Box && b.shape_type == ShapeType::Capsule) {
        Manifold m = manifold_capsule_box(body_capsule(b), body_box(a));
        if (m.valid) m.normal = -m.normal;
        return m;
    }

    return {};
}

} // namespace

void PhysicsWorld::init(u32 max_bodies, Allocator alloc) {
    alloc_ = alloc;

    bodies_ = alloc_.alloc_t<Array<RigidBody>>();
    new (bodies_) Array<RigidBody>(alloc_);
    bodies_->reserve(max_bodies);

    broadphase_.init(max_bodies, alloc_);

    pairs_ = alloc_.alloc_t<Array<Pair>>();
    new (pairs_) Array<Pair>(alloc_);

    constraints_ = alloc_.alloc_t<Array<ContactConstraint>>();
    new (constraints_) Array<ContactConstraint>(alloc_);
}

void PhysicsWorld::shutdown() {
    constraints_->~Array();
    alloc_.free(constraints_, 0);
    constraints_ = nullptr;

    pairs_->~Array();
    alloc_.free(pairs_, 0);
    pairs_ = nullptr;

    broadphase_.shutdown();

    bodies_->~Array();
    alloc_.free(bodies_, 0);
    bodies_ = nullptr;
}

u32 PhysicsWorld::add_body(const RigidBody& b) {
    bodies_->push(b);
    return static_cast<u32>(bodies_->size() - 1);
}

RigidBody& PhysicsWorld::body(u32 id) { return (*bodies_)[id]; }
const RigidBody& PhysicsWorld::body(u32 id) const { return (*bodies_)[id]; }
u32 PhysicsWorld::body_count() const { return static_cast<u32>(bodies_->size()); }

void PhysicsWorld::step(f32 dt) {
    Array<RigidBody>& bodies = *bodies_;

    // 1. Gravity (semi-implicit Euler: velocity updates before position).
    for (usize i = 0; i < bodies.size(); ++i)
        if (bodies[i].inv_mass > 0.0f) bodies[i].linear_velocity += gravity * dt;

    // 2. Broadphase.
    broadphase_.clear();
    for (u32 i = 0; i < bodies.size(); ++i) broadphase_.add(i, body_aabb(bodies[i]));

    pairs_->clear();
    broadphase_.compute_pairs(*pairs_);

    // 3. Narrowphase + manifold -> per-contact-point constraints.
    constraints_->clear();
    for (usize i = 0; i < pairs_->size(); ++i) {
        u32 ia = (*pairs_)[i].a, ib = (*pairs_)[i].b;
        RigidBody& a = bodies[ia];
        RigidBody& b = bodies[ib];
        if (a.inv_mass == 0.0f && b.inv_mass == 0.0f) continue; // two statics: nothing to solve

        Manifold m = compute_manifold(a, b);
        if (!m.valid) continue;

        f32 restitution = a.restitution < b.restitution ? a.restitution : b.restitution;
        f32 friction = std::sqrt(a.friction * b.friction);
        for (u32 p = 0; p < m.count; ++p) {
            ContactConstraint c;
            c.body_a = ia;
            c.body_b = ib;
            c.point = m.points[p].point;
            c.normal = m.normal;
            c.penetration = m.points[p].penetration;
            c.restitution = restitution;
            c.friction = friction;
            constraints_->push(c);
        }
    }

    // 4. Solve velocity constraints.
    prepare_constraints(bodies, *constraints_, dt);
    solve_velocity_constraints(bodies, *constraints_, velocity_iterations);

    // 5. Integrate (semi-implicit Euler: use the just-solved velocities).
    for (usize i = 0; i < bodies.size(); ++i) {
        RigidBody& b = bodies[i];
        if (b.inv_mass <= 0.0f) continue;

        b.position += b.linear_velocity * dt;

        // World-space angular velocity: the composite rotation is "apply the
        // body's current orientation first, then the small world-space spin"
        // (matches quat's documented operator* convention and mat4's
        // row-vector p' = p*M composition order), so the derivative term is
        // orientation * spin_quat, not spin_quat * orientation.
        vec3 w = b.angular_velocity;
        quat spin{w.x, w.y, w.z, 0.0f};
        quat dq = b.orientation * spin;
        quat q = b.orientation;
        q.x += 0.5f * dt * dq.x;
        q.y += 0.5f * dt * dq.y;
        q.z += 0.5f * dt * dq.z;
        q.w += 0.5f * dt * dq.w;
        b.orientation = normalize(q);
    }
}

} // namespace ge::physics
