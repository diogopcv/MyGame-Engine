#pragma once
#include "core/types.h"
#include "core/allocator.h"

/**
 * @file arena.h
 * @brief Linear / bump allocator (Layer 1 — Core).
 *
 * The Arena is the engine's default allocator strategy.  It provides O(1)
 * allocation with no per-object free — all memory is released at once via
 * reset() or shutdown().  Backed by platform::mem_alloc (VirtualAlloc).
 */
namespace ge {

/**
 * @brief Linear (bump) allocator.
 *
 * Allocations advance a simple offset pointer.  Individual frees are not
 * supported; call reset() to reclaim all memory at once (e.g. per-frame
 * scratch data), or shutdown() to release the backing reservation.
 */
class Arena {
public:
    /**
     * @brief Reserve a contiguous virtual-memory block.
     * @param size Number of bytes to reserve.
     */
    void init(usize size);

    /** @brief Release the backing virtual-memory block. */
    void shutdown();

    /**
     * @brief Bump-allocate @p size bytes with the given alignment.
     * @param size  Number of bytes.
     * @param align Alignment requirement (default 16).
     * @return Pointer to the allocated memory, or `nullptr` if out of space.
     */
    void* alloc(usize size, usize align = 16);

    /**
     * @brief Allocate contiguous storage for @p count instances of @p T.
     * @tparam T     Element type.
     * @param  count Number of elements.
     * @return Pointer to uninitialized storage.
     */
    template <typename T>
    T* alloc_n(usize count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    /** @brief Reset the offset to zero, logically freeing all allocations. */
    void reset();

    /** @brief Obtain a type-erased Allocator that dispatches to this Arena. */
    Allocator allocator();

    /** @brief Number of bytes currently in use. */
    usize used()     const { return offset_; }
    /** @brief Total reserved capacity in bytes. */
    usize capacity() const { return size_; }

private:
    u8*   base_   = nullptr;
    usize size_   = 0;
    usize offset_ = 0;
};

} // namespace ge
