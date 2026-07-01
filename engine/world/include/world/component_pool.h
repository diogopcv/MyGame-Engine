#pragma once
#include "core/array.h"
#include "core/allocator.h"
#include "core/assert.h"
#include "world/entity.h"

/**
 * @file component_pool.h
 * @brief Sparse-set component storage (Layer 4 — Frameworks). See ADR-0008.
 */
namespace ge::world {

/** @brief Sentinel meaning "entity has no component in this pool". */
inline constexpr u32 kInvalidDenseIndex = ~u32(0);

/**
 * @brief Type-erased pool interface used only for cross-type bookkeeping
 * (entity-destroy cleanup) — never called in per-component hot loops, so a
 * virtual call here does not violate the "no virtual calls in hot loops" rule.
 */
class IComponentPool {
public:
    virtual ~IComponentPool() = default;

    /** @brief Check whether @p e has a component in this pool. */
    virtual bool has(Entity e) const = 0;

    /** @brief Remove @p e's component from this pool if present; no-op otherwise. */
    virtual void remove_entity(Entity e) = 0;
};

/**
 * @brief Dense per-component-type storage with O(1) add/remove/lookup.
 * @tparam T Component data type (POD-ish; must be move-constructible).
 *
 * Layout: a dense `Array<T>` (contiguous, iteration-friendly) and a parallel
 * dense `Array<Entity>` giving the owning entity of each dense slot, plus a
 * sparse `Array<u32>` mapping `entity.index -> dense index` (or
 * kInvalidDenseIndex). `remove` swaps the removed slot with the last dense
 * slot and fixes up the sparse entry of the entity that moved.
 */
template <typename T>
class ComponentPool : public IComponentPool {
public:
    explicit ComponentPool(Allocator alloc)
        : dense_(alloc), dense_entities_(alloc), sparse_(alloc) {}

    /**
     * @brief Add or overwrite @p e's component.
     * @return Reference to the stored component.
     */
    T& add(Entity e, const T& value) {
        ensure_sparse(e.index);
        u32& slot = sparse_[e.index];
        if (slot != kInvalidDenseIndex) {
            dense_[slot] = value;
            return dense_[slot];
        }
        dense_.push(value);
        dense_entities_.push(e);
        slot = static_cast<u32>(dense_.size() - 1);
        return dense_.back();
    }

    /** @brief Remove @p e's component. No-op if @p e has none. */
    void remove_entity(Entity e) override {
        if (e.index >= sparse_.size()) return;
        u32& slot = sparse_[e.index];
        if (slot == kInvalidDenseIndex) return;

        usize last = dense_.size() - 1;
        if (slot != last) {
            dense_[slot]          = std::move(dense_[last]);
            dense_entities_[slot] = dense_entities_[last];
            sparse_[dense_entities_[slot].index] = slot;
        }
        dense_.pop();
        dense_entities_.pop();
        slot = kInvalidDenseIndex;
    }

    /** @brief Check whether @p e has a component in this pool. */
    bool has(Entity e) const override {
        return e.index < sparse_.size() && sparse_[e.index] != kInvalidDenseIndex;
    }

    /** @brief Look up @p e's component. @return Pointer, or nullptr if absent. */
    T* get(Entity e) {
        if (!has(e)) return nullptr;
        return &dense_[sparse_[e.index]];
    }

    /** @copydoc get(Entity) */
    const T* get(Entity e) const {
        return const_cast<ComponentPool*>(this)->get(e);
    }

    /** @brief Number of live components in this pool. */
    usize size() const { return dense_.size(); }

    /**
     * @brief Invoke @p fn(Entity, T&) for every component in dense order.
     * @tparam Fn Callable with signature `void(Entity, T&)`.
     */
    template <typename Fn>
    void for_each(Fn&& fn) {
        for (usize i = 0; i < dense_.size(); ++i)
            fn(dense_entities_[i], dense_[i]);
    }

    /** @brief Dense entity at slot @p i (for cross-pool joins). */
    Entity dense_entity(usize i) const { return dense_entities_[i]; }
    /** @brief Dense component at slot @p i (mutable, for cross-pool joins). */
    T& dense_value(usize i) { return dense_[i]; }

private:
    void ensure_sparse(u32 index) {
        if (index >= sparse_.size()) {
            usize old = sparse_.size();
            sparse_.resize(index + 1);
            for (usize i = old; i < sparse_.size(); ++i)
                sparse_[i] = kInvalidDenseIndex;
        }
    }

    Array<T>      dense_;
    Array<Entity> dense_entities_;
    Array<u32>    sparse_;
};

} // namespace ge::world
