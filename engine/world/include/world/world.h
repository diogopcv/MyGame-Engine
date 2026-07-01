#pragma once
#include "core/allocator.h"
#include "core/array.h"
#include "core/hash_map.h"
#include "core/assert.h"
#include "world/entity.h"
#include "world/component_pool.h"
#include <new>
#include <utility>

/**
 * @file world.h
 * @brief ECS world: entity lifecycle, component pools, and queries (Layer 4 — Frameworks).
 *
 * See ADR-0008 for the sparse-set storage rationale.
 */
namespace ge::world {

namespace detail {
    /** @brief Monotonic type-id counter (no RTTI; see ADR-0008). */
    inline u64 next_component_type_id() {
        static u64 counter = 0;
        return counter++;
    }

    /**
     * @brief Per-type id, stable for the process lifetime.
     *
     * Each template instantiation gets its own `static const u64 id` storage,
     * so (unlike taking the address of a shared function body) this is safe
     * under MSVC's default Release-mode /OPT:ICF identical-code folding.
     */
    template <typename T>
    u64 component_type_id() {
        static const u64 id = next_component_type_id();
        return id;
    }
}

/**
 * @brief Owns entities and their components; the ECS entry point.
 *
 * Non-copyable. All storage comes from the Allocator passed to init().
 */
class World {
public:
    /**
     * @brief Prepare entity storage and the (initially empty) pool registry.
     * @param max_entities Maximum number of live entities.
     * @param alloc        Allocator for entity and component storage.
     */
    void init(u32 max_entities, Allocator alloc) {
        alloc_ = alloc;
        entities_.init(max_entities, alloc);

        // HashMap/Array are move-only-at-best (HashMap isn't movable at all),
        // so the registry containers are heap-allocated + placement-new'd
        // rather than held by value, mirroring the pImpl style used elsewhere
        // (HandleTable, Window) for lifetime managed through init()/shutdown().
        pools_by_type_ = alloc_.alloc_t<HashMap<u64, void*>>();
        new (pools_by_type_) HashMap<u64, void*>(alloc_);
        all_pools_ = alloc_.alloc_t<Array<IComponentPool*>>();
        new (all_pools_) Array<IComponentPool*>(alloc_);
    }

    /** @brief Destroy all pools and entities, releasing their storage. */
    void shutdown() {
        for (usize i = 0; i < all_pools_->size(); ++i) {
            (*all_pools_)[i]->~IComponentPool();
            alloc_.free((*all_pools_)[i], 0);
        }
        all_pools_->~Array();
        alloc_.free(all_pools_, 0);
        pools_by_type_->~HashMap();
        alloc_.free(pools_by_type_, 0);
        all_pools_     = nullptr;
        pools_by_type_ = nullptr;
        entities_.shutdown();
    }

    /** @brief Create a new entity with no components. */
    Entity create_entity() { return entities_.add(u8(0)); }

    /** @brief Check whether @p e refers to a live entity. */
    bool is_alive(Entity e) const { return entities_.get(e) != nullptr; }

    /** @brief Destroy @p e, removing it from every component pool. Asserts if not alive. */
    void destroy_entity(Entity e) {
        GE_ASSERT(is_alive(e));
        for (usize i = 0; i < all_pools_->size(); ++i)
            (*all_pools_)[i]->remove_entity(e);
        entities_.remove(e);
    }

    /**
     * @brief Add (or overwrite) @p e's component of type T.
     * @return Reference to the stored component.
     */
    template <typename T>
    T& add_component(Entity e, const T& value) {
        GE_ASSERT(is_alive(e));
        return pool<T>().add(e, value);
    }

    /** @brief Remove @p e's component of type T. No-op if absent. */
    template <typename T>
    void remove_component(Entity e) {
        pool<T>().remove_entity(e);
    }

    /** @brief Look up @p e's component of type T. @return Pointer, or nullptr if absent. */
    template <typename T>
    T* get_component(Entity e) {
        return pool<T>().get(e);
    }

    /** @brief Check whether @p e has a component of type T. */
    template <typename T>
    bool has_component(Entity e) {
        return pool<T>().has(e);
    }

    /**
     * @brief Iterate every component of type T in dense storage order.
     * @tparam Fn Callable with signature `void(Entity, T&)`.
     */
    template <typename T, typename Fn>
    void each(Fn&& fn) {
        pool<T>().for_each(std::forward<Fn>(fn));
    }

    /**
     * @brief Iterate every entity that has both A and B (sparse-set join).
     * @tparam Fn Callable with signature `void(Entity, A&, B&)`.
     *
     * Walks the smaller pool's dense array and probes the other via has()/get(),
     * per ADR-0008 — no archetype/query cache is maintained.
     */
    template <typename A, typename B, typename Fn>
    void each(Fn&& fn) {
        auto& pa = pool<A>();
        auto& pb = pool<B>();
        if (pa.size() <= pb.size()) {
            for (usize i = 0; i < pa.size(); ++i) {
                Entity e = pa.dense_entity(i);
                if (B* b = pb.get(e)) fn(e, pa.dense_value(i), *b);
            }
        } else {
            for (usize i = 0; i < pb.size(); ++i) {
                Entity e = pb.dense_entity(i);
                if (A* a = pa.get(e)) fn(e, *a, pb.dense_value(i));
            }
        }
    }

    /** @brief Access (lazily creating) the component pool for type T. */
    template <typename T>
    ComponentPool<T>& pool() {
        u64 id = detail::component_type_id<T>();
        if (void** existing = pools_by_type_->get(id))
            return *static_cast<ComponentPool<T>*>(*existing);

        auto* p = alloc_.alloc_t<ComponentPool<T>>();
        new (p) ComponentPool<T>(alloc_);
        pools_by_type_->set(id, p);
        all_pools_->push(static_cast<IComponentPool*>(p));
        return *p;
    }

private:
    Allocator               alloc_;
    EntityAllocator         entities_;
    HashMap<u64, void*>*    pools_by_type_ = nullptr;
    Array<IComponentPool*>* all_pools_     = nullptr;
};

} // namespace ge::world
