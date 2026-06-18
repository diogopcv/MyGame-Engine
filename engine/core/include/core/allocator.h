#pragma once
#include "core/types.h"

/**
 * @file allocator.h
 * @brief Type-erased allocator interface (Layer 1 — Core).
 *
 * Every container and subsystem accepts an Allocator rather than using a
 * global heap.  Concrete allocators (Arena, Pool, StackAlloc) produce
 * Allocator values via their `allocator()` methods.
 */
namespace ge {

/**
 * @brief Round @p value up to the next multiple of @p align.
 * @param value Value to align.
 * @param align Alignment (must be a power of two).
 * @return Aligned value.
 */
inline usize align_up(usize value, usize align) {
    return (value + (align - 1)) & ~(align - 1);
}

/**
 * @brief Type-erased allocator interface.
 *
 * A lightweight value type carrying function pointers and context.  Concrete
 * allocators (Arena, Pool, StackAlloc) vend Allocator values that dispatch
 * back to themselves.
 */
struct Allocator {
    /** @brief Allocation callback signature. */
    using AllocFn = void* (*)(void* ctx, usize size, usize align);
    /** @brief Deallocation callback signature. */
    using FreeFn  = void  (*)(void* ctx, void* ptr, usize size);

    AllocFn alloc_fn = nullptr; ///< Allocation function pointer.
    FreeFn  free_fn  = nullptr; ///< Deallocation function pointer (may be null for linear allocators).
    void*   ctx      = nullptr; ///< Opaque context passed to alloc_fn / free_fn.

    /**
     * @brief Allocate @p size bytes with the given alignment.
     * @param size  Number of bytes to allocate.
     * @param align Required alignment (default 16).
     * @return Pointer to the allocated memory.
     */
    void* alloc(usize size, usize align = 16) {
        return alloc_fn(ctx, size, align);
    }

    /**
     * @brief Free a previously allocated block.
     * @param ptr  Pointer to free.
     * @param size Size of the original allocation (needed by some backing allocators).
     */
    void free(void* ptr, usize size = 0) {
        if (free_fn) free_fn(ctx, ptr, size);
    }

    /**
     * @brief Allocate memory for a single instance of @p T.
     * @tparam T Type to allocate for.
     * @return Pointer to uninitialized storage for one T.
     */
    template <typename T>
    T* alloc_t() {
        return static_cast<T*>(alloc(sizeof(T), alignof(T)));
    }

    /**
     * @brief Allocate memory for @p count contiguous instances of @p T.
     * @tparam T     Element type.
     * @param  count Number of elements.
     * @return Pointer to uninitialized storage for @p count elements.
     */
    template <typename T>
    T* alloc_n(usize count) {
        return static_cast<T*>(alloc(sizeof(T) * count, alignof(T)));
    }

    /** @brief Check whether this allocator has been initialised. */
    bool is_valid() const { return alloc_fn != nullptr; }
};

} // namespace ge
