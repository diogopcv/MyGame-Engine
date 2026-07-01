#pragma once
#include "core/allocator.h"
#include "core/array.h"
#include "physics/shapes.h"

/**
 * @file broadphase.h
 * @brief Sweep-and-prune broadphase pair generation (Layer 3 — Subsystems).
 *
 * See docs/phases/05-physics.md (Milestone 2). Deliberately the simplest
 * correct structure (a fresh sort-and-sweep every step, no persistent tree);
 * move to a dynamic BVH only if profiling shows this is a bottleneck.
 */
namespace ge::physics {

/** @brief A broadphase-tracked object: a caller-supplied id plus its current AABB. */
struct Proxy {
    u32  id;
    AABB box;
};

/** @brief A candidate overlapping pair, confirmed by full 3D AABB overlap. */
struct Pair {
    u32 a;
    u32 b;
};

/**
 * @brief Sweep-and-prune broadphase over a set of AABBs.
 *
 * Usage per step: clear(), add() every live proxy, then compute_pairs().
 * Sorts proxies by their AABB minimum on the X axis and sweeps forward,
 * confirming full AABB overlap for every candidate whose X-interval
 * overlaps — O(n log n) sort + O(n + k) sweep, where k is the number of
 * overlapping X-intervals (worst case O(n^2) when everything overlaps on X,
 * same as the true output size in that case).
 */
class Broadphase {
public:
    /**
     * @brief Prepare proxy storage.
     * @param max_proxies Maximum number of proxies addable per step.
     * @param alloc       Allocator for proxy storage.
     */
    void init(u32 max_proxies, Allocator alloc);

    /** @brief Release proxy storage. */
    void shutdown();

    /** @brief Drop all proxies added so far (call once per step before re-adding). */
    void clear();

    /** @brief Register a proxy's id and current AABB for this step. */
    void add(u32 id, const AABB& box);

    /**
     * @brief Compute every candidate overlapping pair among the proxies added this step.
     * @param out_pairs Appended to (not cleared first) — caller controls lifetime/reuse.
     */
    void compute_pairs(Array<Pair>& out_pairs);

private:
    Allocator     alloc_;
    Array<Proxy>* proxies_ = nullptr;
};

} // namespace ge::physics
